import kaggle
import os
from pathlib import Path

def download_bdd100k_dataset():
    """下载 BDD100K 数据集"""
    dataset_path = "solesensei/solesensei_bdd100k"
    download_dir = "./datasets/bdd100k"
    
    # 创建下载目录
    Path(download_dir).mkdir(parents=True, exist_ok=True)
    
    print(f"正在下载数据集: {dataset_path}")
    print(f"下载路径: {download_dir}")
    print("注意: BDD100K 数据集可能很大，下载需要一些时间...")
    
    try:
        # 下载并解压数据集
        kaggle.api.dataset_download_files(
            dataset_path,
            path=download_dir,
            unzip=True,
            quiet=False
        )
        
        print(f"\n✓ 数据集下载成功！")
        print(f"文件保存在: {os.path.abspath(download_dir)}")
        
        # 列出下载的文件
        print("\n下载的文件列表:")
        for item in os.listdir(download_dir):
            item_path = os.path.join(download_dir, item)
            if os.path.isdir(item_path):
                print(f"  📁 {item}/")
            else:
                size_mb = os.path.getsize(item_path) / (1024 * 1024)
                print(f"  📄 {item} ({size_mb:.2f} MB)")
        
        return os.path.abspath(download_dir)
        
    except Exception as e:
        print(f"\n✗ 下载失败: {e}")
        print("\n可能的原因:")
        print("1. 数据集可能需要手动接受使用条款")
        print("2. 网络连接问题")
        print("3. 磁盘空间不足")
        return None

if __name__ == "__main__":
    # 下载 BDD100K 数据集
    dataset_path = download_bdd100k_dataset()
    
    if dataset_path:
        print(f"\n✅ 数据集路径: {dataset_path}")
    else:
        print("\n❌ 下载失败，请检查上述错误信息")

    '''
    kaggle datasets download \
  -d robikscube/driving-video-with-object-tracking \
  -p /home/ubuntu/YOLO/datasets/bdd100k_tracking_video \
  --unzip
    '''